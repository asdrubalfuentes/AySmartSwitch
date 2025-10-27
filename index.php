<?php
// Definir la version mas reciente del firmware
$latestVersion = "1.0.12";

// Crear un array con la version
$response = $latestVersion;

// Establecer el encabezado de contenido como texto plano
header('Content-Type: text/plain');

// Devolver la respuesta en formato string
echo $response;
?>