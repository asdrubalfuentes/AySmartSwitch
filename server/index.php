<?php
// Simple firmware publish + version endpoint
// - GET: returns current firmware version as plain text
// - POST (multipart): accepts fields
//     - firmware_version: string like 01.02.125
//     - file: uploaded binary (.bin)
//   Saves: firmware.bin (overwrites) and version.txt with the version
// Optional protection: set environment variable FW_PUBLISH_TOKEN on server and provide header X-Api-Key in POST to require a token.

declare(strict_types=1);

$dir = __DIR__;
$versionFile = $dir . DIRECTORY_SEPARATOR . 'version.txt';
$binaryFile  = $dir . DIRECTORY_SEPARATOR . 'firmware.bin';

function respond_json(int $code, array $payload): void {
    http_response_code($code);
    header('Content-Type: application/json');
    echo json_encode($payload);
    exit;
}

function respond_text(int $code, string $text): void {
    http_response_code($code);
    header('Content-Type: text/plain');
    echo $text;
    exit;
}

$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
if ($method === 'GET') {
    if (file_exists($versionFile)) {
        $ver = trim((string)@file_get_contents($versionFile));
        $ver = $ver !== '' ? $ver : '0.0.0';
        respond_text(200, $ver);
    } else {
        respond_text(200, '0.0.0'); // default when no version yet
    }
}

if ($method === 'POST') {
    // Optional token protection
    $requiredToken = getenv('FW_PUBLISH_TOKEN') ?: '';
    if ($requiredToken !== '') {
        $sent = $_SERVER['HTTP_X_API_KEY'] ?? ($_POST['token'] ?? '');
        if (!hash_equals($requiredToken, (string)$sent)) {
            respond_json(401, ['ok' => false, 'error' => 'unauthorized']);
        }
    }

    // Check multipart fields
    if (!isset($_POST['firmware_version'])) {
        respond_json(400, ['ok' => false, 'error' => 'missing firmware_version']);
    }
    $fwVersion = trim((string)$_POST['firmware_version']);
    if ($fwVersion === '') {
        respond_json(400, ['ok' => false, 'error' => 'empty firmware_version']);
    }

    if (!isset($_FILES['file']) || !is_array($_FILES['file'])) {
        respond_json(400, ['ok' => false, 'error' => 'missing file']);
    }
    $file = $_FILES['file'];
    if (($file['error'] ?? UPLOAD_ERR_NO_FILE) !== UPLOAD_ERR_OK) {
        respond_json(400, ['ok' => false, 'error' => 'upload error', 'code' => $file['error'] ?? -1]);
    }

    // Move uploaded file to firmware.bin
    $tmp = $file['tmp_name'];
    if (!is_uploaded_file($tmp)) {
        respond_json(400, ['ok' => false, 'error' => 'invalid upload']);
    }
    if (!@move_uploaded_file($tmp, $binaryFile)) {
        respond_json(500, ['ok' => false, 'error' => 'failed to save firmware.bin']);
    }

    // Write version
    if (@file_put_contents($versionFile, $fwVersion . "\n") === false) {
        respond_json(500, ['ok' => false, 'error' => 'failed to write version.txt']);
    }

    // Optional: adjust permissions
    @chmod($binaryFile, 0644);
    @chmod($versionFile, 0644);

    respond_json(200, ['ok' => true, 'version' => $fwVersion]);
}

respond_json(405, ['ok' => false, 'error' => 'method not allowed']);
