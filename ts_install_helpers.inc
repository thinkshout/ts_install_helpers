<?php

/**
 * Helper to create permissions for each role.
 * @param - $permissions
 *    An associative array of already instantiated Drupal user roles (usually exported via Features)
 *    Each array contains an array called 'permissions' which includes the machine names of each perm.
 *    In addition, because of the Features bug found at: http://drupal.org/node/1265168#comment-5810726,
 *        we pass in a second array called 'permissions_nocache' that contains arrays keyed off the module names of permissions
 *        we need to inject directly into the database.
 * @TODO - Monitor what's happening with this Features bug so that we can get rid of this latter hack.
 */
function ts_install_helpers_create_perms($permissions = array()) {
  $existing_roles = user_roles(); // Grab an array of roles that have been instantiated to this point.
  foreach($permissions as $name => $role) { // Iterate through our $permissions array.
    foreach($existing_roles as $rid => $existing_role) { // Check for matches, keying off human-readable role name.
      if ($name == $existing_role) {
        // Pass in a role ID and an array of permissions to apply to that role.
        user_role_grant_permissions($rid, $role['permissions']);
        // Then deal with our 'buggy' permissions that need to be inserted directly into the DB.
        if (isset($role['permissions_nocache'])) {
          foreach ($role['permissions_nocache'] as $module => $perms) {
            foreach($perms as $perm) {
              db_merge('role_permission')
                ->key(array(
                'rid' => $rid,
                'permission' => $perm,
              ))
                ->fields(array(
                'module' => $module,
              ))
                ->execute();
            }
          }
        }
      }
    }
  }
}

/**
 * Helper function for creating taxonomy terms and vocabularies.
 * 
 * @param - $stubbed_vocabs
 *   An associative array of taxonomy vocabularies that contain terms.
 *   Supports nested arrays of hierarchical terms, as well as taxonomy term weighting.
 */
function ts_install_helpers_create_terms($stubbed_vocabs = array()) {
  foreach ($stubbed_vocabs as $vocab => $terms) {
    $vocabulary = taxonomy_vocabulary_machine_name_load($vocab);
    // @TODO - Support vocabulary creation in install profile. Currently assumes vocabularies are managed in Features.
    if ($vocabulary) {
      foreach ($terms as $new_term) {
        ts_install_helpers_create_term($vocabulary, $new_term); // Create new terms, recursively.
      }
    }
  }
}

/**
 * Helper function for creating a single taxonomy term and its children, grandchildren, etc.
 */
function ts_install_helpers_create_term($vocabulary, $new_term, $parent_tid = 0) {
  $term = new stdClass();
  $term->name = $new_term['name'];
  $term->vid = $vocabulary->vid;
  $term->format = filter_fallback_format();
  $term->weight = (isset($new_term['weight'])) ? $new_term['weight'] : 0;
  $term->parent = $parent_tid;
  taxonomy_term_save($term);

  // If the current term has children defined, create them recursively.
  if (isset($new_term['children'])) {
    foreach ($new_term['children'] as $child_term) {
      ts_install_helpers_create_term($vocabulary, $child_term, $term->tid);
    }
  }
}


/**
 * Helper function for creating placeholder content.
 *
 * Returns an associative array of menu link items.
 *
 * @param - $stubbed_nodes
 *   An associative array of nodes to be created, keyed off the path of each new node.
 *
 */
function ts_install_helpers_create_content($stubbed_nodes = array()) {
  module_load_include('inc', 'devel_generate');
  module_load_include('inc', 'devel_generate', 'image.devel_generate');
  module_load_include('inc', 'devel_generate', 'devel_generate.fields');
  
  // Default node definition - stuff not captured added the way we want in node_object_prepare().
  $default_node = array(
    'type' => 'page',
    'language' => LANGUAGE_NONE,
    'format' => filter_fallback_format(),
    'uid' => 1,
    'body' => devel_create_greeking(120),
    'link_title' => '',
    'menu_name' => 'main-menu',
    'weight' => 0,
    'parent_path' => '',
  );
  
  $prepared_nodes = array();
  foreach ($stubbed_nodes as $path => $stub) {
    // Merge nodes parameters defined in this install profile with some default values.
    $prepared_nodes[$path] = array_merge($default_node, $stub);
  }
  
  // Generate page nodes, optionally adding to an array of menu items to be created.
  $node_menu_items = array();
  foreach ($prepared_nodes as $path => $prepared_node) {
    $node = new stdClass();
    $node->type = $prepared_node['type'];
    node_object_prepare($node);
    $node->title = $prepared_node['title'];
    $node->uid = $prepared_node['uid'];
    $node->language = $prepared_node['language'];
    $node->body[$node->language][0]['value'] = $prepared_node['body'];
    $node->body[$node->language][0]['summary'] = text_summary($node->body[$node->language][0]['value']);
    $node->body[$node->language][0]['format'] = $prepared_node['format'];
    $node->pathauto_perform_alias = FALSE;
    $node->path = array('alias' => $path, 'pathauto' => FALSE);
    node_save($node);
    
    // If a 'link title' has been set for the node, prep menu link creation.
    if (!empty($prepared_node['link_title'])) {
      $node_menu_items[$path] = array(
        'type' => 'node',
        'link_title' => $prepared_node['link_title'],
        'menu_name' => $prepared_node['menu_name'],
        'language' => $prepared_node['language'],
        'weight' => $prepared_node['weight'],
        'parent_path' => $prepared_node['parent_path'],
      );
    }
  }
  
  return $node_menu_items;
}

/**
 * Helper function for creating menu items.
 *
 * @param - $stubbed_menu_items
 *   An associative array of menu items to be created, keyed off the path of each new menu item.
 *
 */
function ts_install_helpers_create_menu_items($stubbed_menu_items = array()) {

  // Reset the ctools exportables cache in case any of the router items are contained in exportables
  // Specifically needed for links to custom apachesolr_search pages
  if (module_exists('ctools')) {
    ctools_export_load_object_reset();
  }

  // Rebuild prior to starting menu creation to capture menu items built by other modules, like the contact module.
  menu_rebuild();
  
  $default_item = array(
    'type' => 'codefied', // Nodes created in the install profile have 'node' type. Otherwise, assume 'codefied' as default.
    'menu_name' => 'main-menu',
    'weight' => 0,
    'parent_path' => '',
  );
  
  $prepared_menu_items = array();
  $new_menus = array();
  $existing_menus = array_keys(menu_get_menus());
  foreach ($stubbed_menu_items as $path => $stubbed_item) {
    // Merge menu parameters defined in the install profile with some default values.
    $prepared_menu_items[$path] = array_merge($default_item, $stubbed_item);
    // Check if referenced menu exists - if not, we'll create it.
    if (!empty($stubbed_item['menu_name'])) {
      if (!in_array($stubbed_item['menu_name'], $existing_menus)) {
        $new_menus[] = array('menu_name' => $stubbed_item['menu_name']);
      }
    }
  }

  // Create new menus.
  foreach ($new_menus as $menu) {
    if (isset($menu['menu_name'])) {
      $menu['title'] = ucwords(str_replace('-', ' ', $menu['menu_name']));
      $menu['description'] = $menu['title'];
      menu_save($menu);
    }
  }

  // Start creating menu items. @TODO - Debug certain menu item creating steps that require passing in 'module' parameter.
  foreach ($prepared_menu_items as $path => $item) { // First time around the horn, save menu links.
    $prepared_menu_items[$path]['link_path'] = drupal_get_normal_path($path);
    $prepared_menu_items[$path]['mlid'] = menu_link_save($prepared_menu_items[$path]);
  }
  
  foreach ($prepared_menu_items as $item) { // Second time around the horn, set an parent link ids need for menu hierarchy.
    if (!empty($item['parent_path'])) {
      $item['parent_path'] = drupal_get_normal_path($item['parent_path']);
      $plid = db_query("SELECT mlid FROM {menu_links} WHERE menu_name = :menu_name AND link_path = :link_path",
        array(
          ':menu_name' => $item['menu_name'],
          ':link_path' => $item['parent_path'],
        )
      )->fetchfield();
      $item['plid'] = $plid;
      menu_link_save($item);
    }
  }
  
  // Clear the menu cache to get rid of any funkiness.
  menu_cache_clear_all();
}
/**
 * Helper to create beans.
 * @TODO - Add validation and error handling..
 */
function ts_install_helpers_create_beans($stubbed_beans) {  
  foreach ($stubbed_beans as $name => $stubbed_bean) {
    $new_bean = bean_create(array('type' => $stubbed_bean['type']));
    $new_bean->label = $stubbed_bean['label'];
    $new_bean->title = $stubbed_bean['title'];
    $new_bean->delta = $name;
    foreach ($stubbed_bean['properties'] as $prop => $value) {
      $new_bean->{$prop} = $value;
    }
    $new_bean->save();
  }
}

/**
 * Helper to create a test user account for each logged-in role.
 * @param - $gmail_user
 *     A gmail user name for creating placeholder email. Generally, TS creates a Google group for each project,
 *     such as example@thinkshout.com. We use this address to create an account per address, such as example+admin@thinkshout.com.
 * @param - $gmail_domain
 *     Defaults to 'thinkshout.com.', though optionally a different domain can be passed into this helper.
 */
function ts_install_helpers_create_test_users($gmail_user = 'example', $gmail_domain = 'thinkshout.com') {
  $roles = user_roles(TRUE);
  foreach($roles as $rid => $role) {
    $edit = array(); // Collection of parameters to pass to new user account.
    $edit['name'] = $role;
    $edit['mail'] = $gmail_user . '+' . str_replace(" ","_",$role) . '@' . $gmail_domain; // Google test email account.
    $edit['init'] = $edit['mail'];
    $edit['pass'] = $role; // Set the password to the role human-readable name.
    $edit['roles'] = array($rid => $role); // Provide associated role.
    $edit['status'] = 1; // Activate account.
    user_save('', $edit);
  }
}