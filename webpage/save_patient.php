<?php
include "db.php";

$rfid_uid = $_POST['rfid_uid'];
$name = $_POST['name'];
$village = $_POST['village'];
$problem = $_POST['problem'];
$doctor_name = $_POST['doctor_name'];
$visit_date = $_POST['visit_date'];
$visit_time = $_POST['visit_time'];

$token_query = "SELECT COUNT(*) AS total FROM patients WHERE visit_date='$visit_date'";
$token_result = mysqli_query($conn, $token_query);
$row = mysqli_fetch_assoc($token_result);
$token_no = $row['total'] + 1;

$sql = "INSERT INTO patients 
(rfid_uid, name, village, problem, doctor_name, visit_date, visit_time, token_no)
VALUES 
('$rfid_uid', '$name', '$village', '$problem', '$doctor_name', '$visit_date', '$visit_time', '$token_no')";

if (mysqli_query($conn, $sql)) {
    echo "<link rel='stylesheet' href='style.css'>";
    echo "<h1>Patient Registered Successfully</h1>";
    echo "<div class='card'>";
    echo "<p><b>Patient Name:</b> $name</p>";
    echo "<p><b>RFID UID:</b> $rfid_uid</p>";
    echo "<p><b>Token Number:</b> $token_no</p>";
    echo "<p><b>Doctor:</b> $doctor_name</p>";
    echo "<p><b>Date:</b> $visit_date</p>";
    echo "<p><b>Time:</b> $visit_time</p>";
    echo "</div>";
    echo "<a href='index.php'>Go Home</a>";
} else {
    echo "Error: " . mysqli_error($conn);
}
?>
