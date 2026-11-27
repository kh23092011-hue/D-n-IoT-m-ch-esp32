importScripts("https://www.gstatic.com/firebasejs/8.10.1/firebase-app.js");
importScripts("https://www.gstatic.com/firebasejs/8.10.1/firebase-messaging.js");

firebase.initializeApp({
  apiKey: "AIzaSyBAAjIFqe6VmPs6HV6payTUWTL4CG1sXDM",
  authDomain: "dong-dat-esp32.firebaseapp.com",
  databaseURL: "https://dong-dat-esp32-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId: "dong-dat-esp32",
  storageBucket: "dong-dat-esp32.firebasestorage.app",
  messagingSenderId: "523130855246",
  appId: "1:523130855246:web:a67a6c58f78853e29e481f",
  measurementId: "G-4TDTF9JLW6"
});

const messaging = firebase.messaging();

messaging.onBackgroundMessage((payload) => {
  const d = payload.data || {};
  const n = payload.notification || {};

  const title = d.title || n.title || "🚨 CẢNH BÁO TỪ ESP32!";
  const body = d.body || n.body || "Phát hiện rung chấn / khí gas / lửa vượt ngưỡng.";

  return self.registration.showNotification(title, {
    body,
    vibrate: [300, 100, 300, 100, 400],
    tag: "esp32-alarm",
    renotify: true
  });
});
