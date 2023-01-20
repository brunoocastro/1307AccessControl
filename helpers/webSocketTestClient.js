const { Server } = require("ws");

console.log("Iniciou");

const Socket = new Server({ port: 81 });

let IS_DOOR_OPEN = false;
let IS_REGISTER_MODE = false;

function getCurrentStatus() {
  return JSON.stringify({
    type: "getData",
    isDoorOpen: IS_DOOR_OPEN,
    isRegistrationMode: IS_REGISTER_MODE,
  });
}

function updateToAll() {
  const newData = getCurrentStatus();
  console.log("Sending new data", { newData });
  Socket.clients.forEach((client) => {
    client.send(newData);
  });
}

Socket.on("connection", (ws) => {
  console.log("New client connected");

  ws.on("message", (message) => {
    const messageData = JSON.parse(message);
    console.log("Message received: ", messageData);

    switch (messageData.type) {
      case "getData":
        const data = getCurrentStatus();
        console.log("Solicitando dados.", { data });
        ws.send(data);
        break;
      case "isRegistrationMode":
        IS_REGISTER_MODE = Boolean(messageData.value);
        console.log("IS_REGISTER_MODE: ", IS_REGISTER_MODE, messageData.value);
        updateToAll();
        break;
      case "isDoorOpen":
        IS_DOOR_OPEN = Boolean(messageData.value);
        console.log("IS_DOOR_OPEN: ", IS_DOOR_OPEN), messageData.value;
      
        updateToAll();
        break;
    }
  });

  ws.on("close", () => console.log("Client has disconnected!"));
});

// setInterval(() => {
//   Socket.clients.forEach((client) => {
//     IS_DOOR_OPEN = !IS_DOOR_OPEN;
//     IS_REGISTER_MODE = !IS_REGISTER_MODE;
//     const data = JSON.stringify({
//       type: "getData",
//       isDoorOpen: IS_DOOR_OPEN,
//       isRegistrationMode: IS_REGISTER_MODE,
//     });
//     client.send(data);
//   });
// }, 5000);
