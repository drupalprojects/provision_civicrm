<?php

/**
 * @file
 * Hookup with drush
 * Common functions used by install and verify.
 */

/**
 * Expose the service type this extension defines to provision.
 *
 * @return
 *   An array with the service type the key, and the default implementation the value.
 */
function provision_civicrm_provision_services() {
  provision_civicrm_provision_register_autoload();
  return array(
    'civicrm' => NULL,
    'civicrm_db' => NULL,
  );
}

/**
 * Implements hook_civicrm_init()
 */
function provision_civicrm_civicrm_init() {
  // Log in a 'drush' subdirectory
  return provision_civicrm_setup_temp_directories();
}

/**
 * Setup logs and template_c in a sub-directory to avoid permission
 * conflicts between the www-user and the aegir user.
 */
function provision_civicrm_setup_temp_directories() {
  require_once 'CRM/Core/Config.php';
  $config = CRM_Core_Config::singleton( );
  $config->configAndLogDir = $config->configAndLogDir . 'drush/';

  // ConfigAndLog directory
  $exists = provision_file()->exists($config->configAndLogDir)
    ->succeed('CiviCRM: @path exists.')
    ->status();

  if (! $exists) {
    $exists = provision_file()->mkdir($config->configAndLogDir)
      ->succeed('CiviCRM: log path @path has been created.')
      ->fail('CiviCRM: log path @path could not be created.', 'DRUSH_PERM_ERROR')
      ->status();
  }

  if (! $exists) {
    return FALSE;
  }

  // Smarty template directory
  $config->templateCompileDir = $config->templateCompileDir . 'drush/';

  $exists = provision_file()->exists($config->templateCompileDir)
    ->succeed('CiviCRM: @path exists.')
    ->status();

  if (! $exists) {
    $exists = provision_file()->mkdir($config->templateCompileDir)
      ->succeed('CiviCRM: compiled template path @path has been created.')
      ->fail('CiviCRM: compiled template path @path could not be created.', 'DRUSH_PERM_ERROR')
      ->status();
  }

  if (! $exists) {
    return FALSE;
  }

  return TRUE;
}

/**
 * Create a civicron user and role
 */
function _provision_civicrm_create_civicron_user() {
  // Since 4.1, not necessary to create a dedicated user anymore
  // since we can run the cron as admin.
  $dbVer = _provision_civicrm_database_version();

  if (substr($dbVer, 0, 3) >= '4.1') {
    return;
  }

  if (! function_exists("drush_user_create")) {
    drush_log(dt("Not running drush 4.x or above, so cannot create civicron user and role. If you want to run the CiviCRM crons using drush, you will have to create them manually. Once you have done so, you can edit the site's sites/example.org/drushrc.php and set the correct username and password for the cron."));
    drush_set_option('civicrm_cron_username', 'civicron', 'site');
    drush_set_option('civicrm_cron_password', NULL, 'site');
    return FALSE;
  }

  $url = drush_get_option('uri', false);

  $civicron_password = provision_password();

  drush_set_option('mail', 'civicrm-no-reply@' . $url);
  drush_set_option('password', $civicron_password);
  drush_user_create('civicron');

  drush_set_option('civicrm_cron_username', 'civicron', 'site');
  drush_set_option('civicrm_cron_password', $civicron_password, 'site');

  // Create civicron role
  if (function_exists('user_role_save')) {
    // D7
    $civicron_role = new stdClass;
    $civicron_role->name = 'civicron';
    user_role_save($civicron_role);
  }
  else {
    db_query("INSERT INTO {role} (name) VALUES ('%s')", array('civicron'));
  }

  drush_user_add_role('civicron', 'civicron');

  // Assign permissions to civicron role
  $all_roles = user_roles(); // effectively same for D6/7
  $civicron_rid = array_search('civicron', $all_roles);

  if (function_exists('user_role_grant_permissions')) {
    // D7
    $all_perms = user_permission_get_modules();
    $civi_perms = array_keys($all_perms, 'civicrm'); // todo: this is just the main civicrm module
    user_role_grant_permissions($civicron_rid, $civi_perms);
  }
  else {
    $civicrm_permissions = 'access CiviCRM, access CiviContribute, access CiviEvent, access CiviMail, access CiviMail subscribe/unsubscribe pages, access CiviMember, access CiviPledge, access CiviReport, access Contact Dashboard, access Report Criteria, access all custom data, add contacts, edit all contacts, edit all events, edit contributions, edit event participants, view all activities, view all contacts, view all notes';
    db_query("INSERT INTO {permission} (rid, perm) VALUES (%d, '%s')", array($civicron_rid, $civicrm_permissions));
  }

  return TRUE;
}


/**
 * Fix permissions on all CiviCRM writable directories
 */
function _provision_civicrm_fixpermissions() {
  drush_log(dt("CiviCRM: in _provision_civicrm_fixpermissions()"));

  // Make sure the template_c and uploads have the correct permissions
  $civipaths = array(
    '/files/civicrm/templates_c',
    '/files/civicrm/upload',
    '/files/civicrm/persist',
    '/files/civicrm/custom',
    '/files/civicrm/ConfigAndLog',
  );

  // New in CiviCRM 4.5 (avoid warnings for CiviCRM < 4.5)
  if (file_exists(d()->site_path . '/files/civicrm/dynamic')) {
    $civipaths[] = '/files/civicrm/dynamic';
  }

  foreach ($civipaths as $p) {
    $fullpath = d()->site_path . $p;

    // we setgid directories, avoids a few permissions issues
    // where files are aegir.www-data.
    provision_file()->chmod($fullpath, 02770, TRUE)
      ->succeed('Changed permissions of @path to @perm')
      ->fail('Could not change permissions <code>@path to @perm')
      ->status();

    provision_file()->chgrp($fullpath, d('@server_master')->web_group, TRUE)
      ->succeed('Changed group ownership @path to @gid')
      ->fail('Could not change group ownership of @path to @gid')
      ->status();
  }

  // Fix permissions on civicrm.settings.php
  provision_file()->chmod(d()->site_path . '/civicrm.settings.php', 0640)
    ->succeed('Changed permissions of @path to @perm')
    ->fail('Could not change permissions <code>@path to @perm')
    ->status();

  provision_file()->chgrp(d()->site_path . '/civicrm.settings.php', d('@server_master')->web_group, TRUE)
    ->succeed('Changed group ownership @path to @gid')
    ->fail('Could not change group ownership of @path to @gid')
    ->status();

  // Remove the Config.IDS.ini file (d.o#1226668)
  // Usually 'verify' will call civicrm_cache_clear() which will
  // delete most of the contents of template_c, but there have been
  // some hard to trace bugs, so we are not taking any chances.
  //
  // Also, civicrm_cache_clear() assumes that _civi_init() has been
  // done, and if we init, then the Config.IDS.ini will be parsed.
/* [ML] do not rely on _civi_init() to have been run yet.
  require_once 'CRM/Core/Config.php';
  $config = CRM_Core_Config::singleton( );
  $configFile = $config->configAndLogDir . 'Config.IDS.ini';
*/

  $configFiles = array(
    '/files/civicrm/ConfigAndLog/Config.IDS.ini',
    '/files/civicrm/drush/ConfigAndLog/Config.IDS.ini',
    '/files/civicrm/templates_c/en_US/ConfigAndLog/Config.IDS.ini',
  );

  foreach ($configFiles as $c) {
    $ids = d()->site_path . $c;

    if (provision_file()->exists($ids)->status()) {
      provision_file()->unlink($ids)
        ->succeed('CiviCRM: Removing @path')
        ->fail('CiviCRM: Could not remove @path')
        ->status();
    }
  }

  drush_log(dt("CiviCRM: finished _provision_civicrm_fixpermissions()"));
}

/**
 * Update the CiviCRM Resource URLs
 * There must be a much simpler/cleaner way of doing this..
 */
function _provision_civicrm_fixresourceurls() {
  drush_log(dt("CiviCRM: Running civicrm_config_update() to fix URL/path settings."));

  if (function_exists('drush_civicrm_update_cfg')) {
    drush_civicrm_update_cfg_validate();
    drush_civicrm_update_cfg();
  }
  elseif (function_exists('civicrm_config_update')) {
    civicrm_config_update();
  }
  else {
    drush_log(dt("CiviCRM: Could not find a drush function to update the CiviCRM url/path settings."), 'error');
    return;
  }

  drush_log(dt("CiviCRM: Finished civicrm_config_update()."));
}

/**
 * Wrapper to update values in {civicrm_option_value}
 */
function _provision_civicrm_update_option_value($key, $val) {
  switch (drush_drupal_major_version()) {
    case 6:
      $oldval = drush_db_result(db_query("SELECT value FROM {civicrm_option_value} WHERE name = '%s'", $key));
      drush_log(dt('CiviCRM: before UPDATE: old val for @key = @oldval', array('@key' => $key, '@oldval' => $oldval)));

      db_query("UPDATE {civicrm_option_value} set value = '%s' where name = '%s'", array($val, $key));
      drush_log(dt("CiviCRM: updated @key = @val", array('@key' => check_plain($key), '@val' => check_plain($val))));
      break;
    case 7:
      $oldval = drush_db_result(db_query("SELECT value FROM {civicrm_option_value} WHERE name = :key", array(':key' => $key)));
      drush_log(dt('CiviCRM: before UPDATE: old val for @key = @oldval', array('@key' => $key, '@oldval' => $oldval)));

      db_query("UPDATE {civicrm_option_value} set value = :val where name = :key", array(':val' => $val, ':key' => $key));
      drush_log(dt("CiviCRM: updated @key = @val", array('@key' => check_plain($key), '@val' => check_plain($val))));
      break;
  }
}

/**
 * Get an array of civicrm related options
 *
 * @param $op
 *   A string describing the task being run, for debugging purposes
 */
function _provision_civicrm_drush_options($op = 'unknown') {
  drush_log(dt("CiviCRM: In @op: Fetching CiviCRM Drush options", array('@op' => $op)));

  $civi_opts = array(
    'civicrm_cron_username' => '',
    'civicrm_cron_password' => '',
    'civicrm_sitekey' => '',
  );

  foreach ($civi_opts as $opt => $val) {
    $civi_opts[$opt] = drush_get_option($opt, NULL);

    /* Debugging code follows, comment out this line to enable it.
    if ($civi_opts[$opt] == NULL) {
      drush_set_error('PROVISION_CIVICRM_DRUSH_OPTIONS', "In $op: Failed to get valid $opt");
    }
    $dump = var_export($civi_opts[$opt], true);
    drush_log(dt("In @op: $dump retrieved for @opt", array('@op' => $op, '@opt' => $opt)), 'warning');
    //*/
  }

  return $civi_opts;
}

/**
 * Check we are in an Aegir site context, and that CiviCRM is available in the platform.
 *
 * @param Boolean $load_if_missing Recommended in most cases. See do#2364871.
 * @see _provision_civicrm_get_package_path();
 */
function _provision_civicrm_is_site_context($load_if_missing = FALSE) {
  if (d()->type == 'site') {
    $crmpath = _provision_civicrm_get_package_path($load_if_missing);
    return ($crmpath ? TRUE : FALSE);
  }
  return FALSE;
}

/**
 * Get the path where the CiviCRM module is installed
 * Ex: /var/aegir/platforms/[...]/sites/all/modules/civicrm
 *
 * NB: if CiviCRM was not detected, verify the platform.
 *
 * @param Boolean $load_if_missing Recommended in most cases. See do#2364871.
 */
function _provision_civicrm_get_package_path($load_if_missing = FALSE) {
  $module = _provision_civicrm_get_package_info();

  if ($module) {
    // Get the parent directory of the module, which is in civicrm/drupal/civicrm.module
    $crmpath = dirname(dirname($module['filename']));
    drush_log(dt('CiviCRM: civicrm is in @path', array('@path' => $crmpath)));

    // do#2364871 Workaround a BOA 2.3 issue
    // When civicrm is in an install profile, the civicrm drush module does not get loaded.
    if ($load_if_missing && ! function_exists('_civicrm_get_crmpath')) {
      drush_log(dt('CiviCRM: loading file: civicrm.drush.inc.'));
      require_once($crmpath . '/drupal/drush/civicrm.drush.inc');
    }

    return $crmpath;
  }

  return FALSE;
}

/**
 * Get the CiviCRM module base information (filename, basename, version, etc)
 */
function _provision_civicrm_get_package_info() {
  $module = NULL;

  // Check if the CiviCRM code base is present in the platform
  // Note: after putting the code there, you need to verify
  // the platform.
  $packages = drush_get_option('packages', array('modules' => NULL));
  $profile = drush_get_option('profile', NULL);

  if (isset($packages['modules']) && isset($packages['modules']['civicrm'])) {
    drush_log(dt('CiviCRM: found civicrm in packages'));
    $module = $packages['modules']['civicrm'];
  }
  elseif (isset($packages['sites-all']) && isset($packages['sites-all']['modules']) && isset($packages['sites-all']['modules']['civicrm'])) {
    $module = $packages['sites-all']['modules']['civicrm'];
    drush_log(dt('CiviCRM: found civicrm in packages of sites-all modules'));
  }
  elseif (isset($packages['base']) && isset($packages['base']['modules']) && isset($packages['base']['modules']['civicrm'])) {
    $module = $packages['base']['modules']['civicrm'];
    drush_log(dt('CiviCRM: found civicrm in packages of base modules'));
  }
  elseif ($profile && isset($packages['profiles'][$profile]['modules']) && isset($packages['profiles'][$profile]['modules']['civicrm'])) {
    $module = $packages['profiles'][$profile]['modules']['civicrm'];
    drush_log(dt('CiviCRM: found civicrm in packages of @profile profile modules', array('@profile' => $profile)));
  }
  else {
    // drush_log(dt('CiviCRM: civicrm not found in packages ' . print_r($packages, 1)));
  }

  // drush_log('CiviCRM: MODULE = ' . print_r($module, 1));

  return $module;
}

/**
 * Wrapper function for getConfigSettings (class location changed in 4.1)
 */
function provision_civicrm_get_config_settings() {
  require_once 'CRM/Utils/System.php';
  $codeVer = CRM_Utils_System::version();

  if (substr($codeVer, 0, 3) >= '4.1') {
    require_once 'CRM/Core/BAO/ConfigSetting.php';
    return CRM_Core_BAO_ConfigSetting::getConfigSettings();
  }

  // else
  require_once 'CRM/Core/BAO/Setting.php';
  return CRM_Core_BAO_Setting::getConfigSettings();
}

/**
 * Wrapper function for getBestGuessSettings (class location changed in 4.1)
 */
function provision_civicrm_get_best_guess_settings() {
  require_once 'CRM/Utils/System.php';
  $codeVer = CRM_Utils_System::version();

  if (substr($codeVer, 0, 3) >= '4.1') {
    require_once 'CRM/Core/BAO/ConfigSetting.php';
    return CRM_Core_BAO_ConfigSetting::getBestGuessSettings();
  }

  // else
  require_once 'CRM/Core/BAO/Setting.php';
  return CRM_Core_BAO_Setting::getBestGuessSettings();
}

/**
 * CiviCRM verify operations (used by both verify and deploy)
 */
function _provision_civicrm_regenerate_settings() {
  drush_log(dt("CiviCRM: starting provision_civicrm_regenerate_settings()."));

  // Make sure to write the civicrm options
  $civi_opts = _provision_civicrm_drush_options('post verify');
  foreach ($civi_opts as $opt => $val) {
    drush_set_option($opt, $val, 'site');
  }

  // Since we do not have an upgrade path for existing site key (before it
  // was added to the frontend), be nice about an empty sitekey.
  if (d()->civicrm_sitekey) {
    drush_log('CiviCRM: found a site key from the front-end.', 'ok');
    drush_set_option('civicrm_sitekey', d()->civicrm_sitekey);
  }
  else {
    drush_log('CiviCRM: no site key found from the front-end.');
  }

  $db_user = drush_get_option('db_user', NULL, 'site');
  $db_passwd = drush_get_option('db_passwd', NULL, 'site');
  $db_host = drush_get_option('db_host', NULL, 'site');
  $db_name = drush_get_option('db_name', NULL, 'site');

  // include civicrm installer helper file (otherwise PEAR DB.php will fail to include parts)
  // FIXME : we should test whether this is still necessary.
  $drupalRoot = drush_get_context('DRUSH_DRUPAL_ROOT');
  $crmPath = _provision_civicrm_get_package_path();

  $civicrmInstallerHelper = $crmPath . "/install/civicrm.php";

  if (! file_exists($civicrmInstallerHelper) ) {
    drush_log(dt("CiviCRM installer helper file is missing. Not running provision_civicrm_regenerate_settings()."));
    return;
  }

  require_once "$civicrmInstallerHelper";

  provision_civicrm_regenerate_settings_file($db_user, $db_passwd, $db_host, $db_name, $crmPath);
  drush_log(dt("CiviCRM: Generated config civicrm.settings.php file"), 'ok');
  drush_log(dt("CiviCRM: leaving provision_civicrm_regenerate_settings()."));
}

/**
 * Regenerates the civicrm.settings.php file.
 * Forked from civicrm.drush.inc's _civicrm_generate_settings_file().
 *
 * Modified mostly so that we can make the sitekey persist,
 * and replaced th $modPath argument by $crmPath.
 */
function provision_civicrm_regenerate_settings_file($dbuser, $dbpass, $dbhost, $dbname, $crmPath) {
  $sitekey = d()->civicrm_sitekey;
  $drupalRoot = d()->root;
  $siteRoot   = d()->site_path;
  $crmPath    = _provision_civicrm_get_package_path();

  $files = array(
    "$crmPath/templates/CRM/common/civicrm.settings.php.template",
    "$crmPath/templates/CRM/common/civicrm.settings.php.tpl"
  );

  $settingsTplFile = NULL;
  foreach ($files as $file) {
    if (file_exists($file)) {
      $settingsTplFile = $file;
    }
  }

  if (!$settingsTplFile) {
    drush_die(dt("Could not find CiviCRM settings template and therefore could not create settings file."));
  }

  drush_log(dt("CiviCRM: Generating the settings file .."), 'ok');

  $protocol = (d()->ssl_enabled > 0 ? 'https' : 'http');
  $baseUrl = $protocol . '://' . d()->uri;
  $drush_major_version = substr(DRUSH_VERSION, 0, 1);

  // #2411137 NB: this tests the Drush version, not Drupal.
  switch ($drush_major_version) {
    case '7':
      $sql = drush_get_class('Drush\Sql\Sql', array(), array(drush_drupal_major_version()));
      $db_spec = $sql->get_db_spec();
      break;
    case '6':
    case '5':
      $db_spec = _drush_sql_get_db_spec();
      break;
    default:
      drush_set_error('DRUSH_UNSUPPORTED_VERSION', dt('Drush !version is not supported', array('!version' => DRUSH_VERSION)));
  }

  // Check version: since 4.1, Drupal6 must be used for the UF in D6
  // The file civicrm-version.php appeared around 4.0, so it is safe to assume
  // that if it's not there, it's 3.x, in which case the CMS 'Drupal' is D6.
  $cms = 'Drupal';

  if (file_exists("$crmPath/civicrm-version.php")) {
    require_once "$crmPath/civicrm-version.php";
    $v = civicrmVersion();
    $cms = $v['cms'];
  }

  $params = array(
    'crmRoot' => $crmPath,
    'templateCompileDir' => "$siteRoot/files/civicrm/templates_c",
    'frontEnd' => 0,
    'cms' => $cms,
    'baseURL' => $baseUrl,
    'dbUser' => $dbuser,
    'dbPass' => $dbpass,
    'dbHost' => $dbhost,
    'dbName' => $dbname,
    'CMSdbUser' => $db_spec['username'],
    'CMSdbPass' => $db_spec['password'],
    'CMSdbHost' => $db_spec['host'],
    'CMSdbName' => $db_spec['database'],
    'siteKey' => $sitekey,
  );
  $str = file_get_contents($settingsTplFile);
  foreach ($params as $key => $value) {
    $str = str_replace('%%' . $key . '%%', $value, $str);
  }
  $str = trim($str);

  $configFile = "$siteRoot/civicrm.settings.php";
  civicrm_write_file($configFile, $str);
  @drush_op('chmod', "$configFile", 0644);
  drush_log(dt("Settings file generated: !file", array('!file' => $configFile)), 'ok');
}

function provision_civicrm_verify_common() {
  drush_log(dt("CiviCRM: in provision_civicrm_verify_common()."));

  // Set file acls on civicrm.settings.php, if necessary
  if (function_exists('provisionacl_set_acl')) {
    $group = d()->client_name;
    $file = d()->site_path . '/' . 'civicrm.settings.php';
    drush_log(dt('CiviCRM: Attempting to set acls for @group on @file', array('@group' => $group, '@file' => $file)));

    // Check the group
    if (!provision_posix_groupname($group)) {
      drush_log(dt('CiviCRM: not setting acls on @file for non-existent group @group', array('@file' => $file, '@group' => $group)), 'warning');
    }
    // Check the file
    else if (! file_exists($file)) {
      drush_log(dt('@file not found, acls not configured', array('@file' => $file)), 'warning');
    }
    else {
      provisionacl_set_acl('group', $group, 'r--', array($file), TRUE);
    }
  }

  drush_set_option('oldVal_1', drush_get_option('civicrm_old_path'));
  drush_set_option('newVal_1', drush_get_context('DRUSH_DRUPAL_ROOT'));

  drush_set_option('oldVal_2', drush_get_option('civicrm_old_baseurl'));
  drush_set_option('newVal_2', 'http://' . drush_get_option('uri'));

  // Update database settings for the directory paths
  _provision_civicrm_do_site_move();

  // Update Resource URLs
  _provision_civicrm_fixresourceurls();

  // Make sure the template_c and uploads have the correct permissions
  _provision_civicrm_fixpermissions();

  drush_log(dt("CiviCRM: leaving provision_civicrm_verify_common()."));
  return TRUE;
}

/**
 * CiviCRM database upgrade
 *
 * Code forked from the CiviCRM drush module
 * Adapted to check the upgrade result and support multiple CiviCRM versions.
 */
function _provision_civicrm_upgradedb() {
  drush_log(dt("CiviCRM: in _provision_civicrm_upgradedb(), calling _civicrm_init()"));

  // Check for the CiviCRM installer helper, to ignore the upgrade
  // on really old versions of civicrm (verify also does this).
  $crmPath = _provision_civicrm_get_package_path();

  $civicrmInstallerHelper = $crmPath . "/install/civicrm.php";

  if (! file_exists($civicrmInstallerHelper) ) {
    drush_log(dt("CiviCRM installer helper file is missing. Old CiviCRM version?"));
    return;
  }

  if (! defined('CIVICRM_UPGRADE_ACTIVE')) {
    define('CIVICRM_UPGRADE_ACTIVE', 1);
  }

  _civicrm_init();

  drush_log(dt("CiviCRM: in _provision_civicrm_upgradedb(), _civicrm_init() worked, comparing code and schema versions."));

  // This was added around CiviCRM >= 4.2. Use it if possible.
  // Does a few version checks before the upgrade.
  // FIXME: should we use drush_invoke or equivalent?
  if (function_exists('drush_civicrm_upgrade_db_validate')) {
    if (! drush_civicrm_upgrade_db_validate()) {
      return FALSE; // above already does a drush_set_error
    }
  }

  // Still need to check if the DB and code versions are the same
  // so that we can avoid calling the upgrade on 'verify'.
  $codeVer = _provision_civicrm_codebase_version();
  $dbVer   = _provision_civicrm_database_version();

  if (version_compare($codeVer, $dbVer) == 0) {
    drush_log(dt("CiviCRM: no upgrade required (@codeVer = @dbVer)", array('@codeVer' => $codeVer, '@dbVer' => $dbVer)), 'ok');
    return;
  }

  if (function_exists('drush_civicrm_upgrade_db')) {
    ob_start();
    drush_civicrm_upgrade_db();
    $output = ob_get_contents();
    drush_log(dt("CiviCRM: upgrade output: !output", array('!output' => $output)), 'warning');
    ob_end_clean();
  }
  elseif (function_exists('civicrm_upgrade_db')) {
    // CiviCRM < 4.2 ?
    ob_start();
    civicrm_upgrade_db();
    $output = ob_get_contents();
    drush_log(dt("CiviCRM: upgrade output: !output", array('!output' => $output)), 'warning');
    ob_end_clean();
  }
  else {
    drush_log(dt("CiviCRM: Could not find a drush function to upgrade CiviCRM."), 'error');
    return;
  }

  drush_log(dt("CiviCRM: finished _provision_civicrm_upgradedb()"));
}

/**
 * EXPERIMENTAL AND EVIL: forking the code from civicrm_update_config()
 * and CRM/Core/BAO/Setting.php doSiteMove() because doSiteMove() in
 * 4.1.1 doSiteMove() flushes the cache in a brutal way, especially on
 * tables that do not yet exist.
 *
 * We need to propose a cleaner fix to CiviCRM core so that we do not
 * need to maintain this type of forked code. TODO FIXME
 */
function _provision_civicrm_do_site_move($defaultValues = array()) {
  drush_log(dt("CiviCRM: in _provision_civicrm_do_site_move()."));

  // See CRM_Core_Config::isUpgradeMode() for more information
  if (! defined('CIVICRM_UPGRADE_ACTIVE')) {
    define('CIVICRM_UPGRADE_ACTIVE', 1);
  }

  //
  // from drush civicrm_config_update()
  //
  _civicrm_init();

  $defaultValues = array( );
  $states = array( 'old', 'new' );
  for ( $i = 1 ; $i <= 3; $i++ ) {
    foreach ( $states as $state ) {
      $name = "{$state}Val_{$i}";
      $value = drush_get_option( $name, null );
      if ( $value ) {
        $defaultValues[$name] = $value;
      }
    }
  }

  require_once 'CRM/Core/I18n.php';

  //
  // from doSiteMove
  //

  $moveStatus = ts('Beginning site move process...') . '<br />';

  // get the current and guessed values
  list($oldURL, $oldDir, $oldSiteName, $oldSiteRoot) = provision_civicrm_get_config_settings();
  list($newURL, $newDir, $newSiteName, $newSiteRoot) = provision_civicrm_get_best_guess_settings();

  require_once 'CRM/Utils/Request.php';

  // retrieve these values from the argument list
  $variables = array( 'URL', 'Dir', 'SiteName', 'SiteRoot', 'Val_1', 'Val_2', 'Val_3' );
  $states     = array( 'old', 'new' );
  foreach ( $variables as $varSuffix ) {
      foreach ( $states as $state ) {
          $var = "{$state}{$varSuffix}";
          if ( ! isset( $$var ) ) {
              if ( isset( $defaultValues[$var] ) ) {
                  $$var = $defaultValues[$var];
              } else {
                  $$var = null;
              }
          }
          $$var = CRM_Utils_Request::retrieve( $var,
                                               'String',
                                               CRM_Core_DAO::$_nullArray,
                                               false,
                                               $$var,
                                               'REQUEST' );
      }
  }

  $from = $to = array( );
  foreach ( $variables as $varSuffix ) {
      $oldVar = "old{$varSuffix}";
      $newVar = "new{$varSuffix}";
      //skip it if either is empty or both are exactly the same
      if ( $$oldVar &&
           $$newVar &&
           $$oldVar != $$newVar ) {
          $from[]  = $$oldVar;
          $to[]    = $$newVar;
      }
  }

  $sql = "
SELECT config_backend
FROM   civicrm_domain
WHERE  id = %1
";
  $params = array( 1 => array( CRM_Core_Config::domainID( ), 'Integer' ) );
  $configBackend = CRM_Core_DAO::singleValueQuery( $sql, $params );
  if ( ! $configBackend ) {
      CRM_Core_Error::fatal( ts('Returning early due to unexpected error - civicrm_domain.config_backend column value is NULL. Try visiting CiviCRM Home page.') );
  }
  $configBackend = unserialize( $configBackend );

  $configBackend = str_replace( $from,
                                $to  ,
                                $configBackend );

  $configBackend = serialize( $configBackend );
  $sql = "
UPDATE civicrm_domain
SET    config_backend = %2
WHERE  id = %1
";
  $params[2] = array( $configBackend, 'String' );
  CRM_Core_DAO::executeQuery( $sql, $params );

  // Apply the changes to civicrm_option_values
  $optionGroups = array('url_preferences', 'directory_preferences');
  foreach ($optionGroups as $option) {
      foreach ( $variables as $varSuffix ) {
          $oldVar = "old{$varSuffix}";
          $newVar = "new{$varSuffix}";

          $from = $$oldVar;
          $to   = $$newVar;

          if ($from && $to && $from != $to) {
              $sql = '
UPDATE civicrm_option_value
SET    value = REPLACE(value, %1, %2)
WHERE  option_group_id = (
SELECT id
FROM   civicrm_option_group
WHERE  name = %3 )
';
              $params = array( 1 => array ( $from, 'String' ),
                               2 => array ($to, 'String'),
                               3 => array($option, 'String') );
              CRM_Core_DAO::executeQuery( $sql, $params );
          }
      }
  }

  // [ML] not really necessary? we cache clear anyway afterwards..
  // $config =& CRM_Core_Config::singleton( );

  // clear the template_c and upload directory also
  // $config->cleanup( 3, true );

  drush_log(dt("CiviCRM: finished _provision_civicrm_do_site_move()."));
  return $moveStatus;
}

/**
 * Check whether to set the upgrade mode
 */
function _provision_civicrm_check_upgrade_mode() {
  // Sets the civicrm upgrade mode of the code/db version do not match
/*
  $codeVer = _provision_civicrm_codebase_version();
  $dbVer = _provision_civicrm_database_version();

  if (version_compare($codeVer, $dbVer) != 0) {
    if (! defined('CIVICRM_UPGRADE_ACTIVE')) {
      define('CIVICRM_UPGRADE_ACTIVE', 1);
      drush_log(dt('CiviCRM: upgrade mode enabled.'));
      return TRUE;
    }
  }
*/

  if (! defined('CIVICRM_UPGRADE_ACTIVE')) {
    define('CIVICRM_UPGRADE_ACTIVE', 1);
    drush_log(dt('CiviCRM: upgrade mode enabled.'));
    return TRUE;
  }

  return FALSE;
}

/**
 * Helper function to get the CiviCRM version of the code base
 */
function _provision_civicrm_codebase_version() {
  static $civicrm_version = 0;

  if ($civicrm_version) {
    return $civicrm_version;
  }

  _civicrm_init();
  require_once 'CRM/Utils/System.php';

  if (method_exists('CRM_Utils_System', 'version')) {
    $civicrm_version = CRM_Utils_System::version();
  }

  return $civicrm_version;
}

/**
 * Helper function to get the CiviCRM version of the DB
 */
function _provision_civicrm_database_version() {
  $dbVer = 0;
  $civicrmSettingsFile = d()->site_path . "/civicrm.settings.php";
  include_once($civicrmSettingsFile);

  switch (drush_drupal_major_version()) {
    case 6:
      $dbVer = db_result(db_query('select version from {civicrm_domain} where id = %d', CIVICRM_DOMAIN_ID));
      break;

    case 7:
      $dbVer = db_query('select version from {civicrm_domain} where id = :id', array(':id' => CIVICRM_DOMAIN_ID))->fetchField();
      break;
  }

  drush_log(dt('CiviCRM: db version is !ver', array('!ver' => $dbVer)));
  return $dbVer;
}

