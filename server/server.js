const io = require('socket.io')(4098)

let rooms = {}

//Periodically send out refresh data
setInterval(refresh, 10000)

//When connected
io.on('connection', (client) => {
	console.log('Client joining!')
	//When they register
	client.on('register', function(msg) {
		console.log("Adding to room")
		//Parse data and save to the proper room
		let info = msg.split(",")
		if (!rooms[info[0]]) {
			rooms[info[0]] = {}
		}
		rooms[info[0]][client.id] = {
			board_id: info[1],
			name: info[2],
			broadcasting: false
		}
		//Add them to the room
		client.join(info[0])
		//Broadcast
		io.in(info[0]).emit('room', formatRoom(rooms[info[0]]))
		console.log(rooms)
	})

	client.on('zap', function(msg) {
		console.log("Zap request received")
		let data = msg.split(',')
		//Update data to reflect zap and broadcast
		for (let i in rooms) {
			if (rooms[i][client.id]) {
				rooms[i][client.id].broadcasting = true
				io.in(i).emit('room', formatRoom(rooms[data[0]]))
			}
		}
		console.log(rooms)
	})

	client.on('unzap', function(msg) {
		console.log("Zap request received")
		let data = msg.split(',')
		//Same as above but the opposite
		for (let i in rooms) {
			if (rooms[i][client.id]) {
				rooms[i][client.id].broadcasting = false
				io.in(i).emit('room', formatRoom(rooms[data[0]]))
			}
		}
		console.log(rooms)
	})

	//Remove client from rooms on disconnect
	client.on('disconnect', function() {
		console.log('Client leaving!')
		for (let i in rooms) {
			if (rooms[i][client.id]) {
				delete rooms[i][client.id]
			}
			io.in(i).emit('room', formatRoom(rooms[i]))
		}
		console.log(rooms)
	})
})

//Turns a room object into a specific string representation
function formatRoom(room) {
	let toReturn = ""
	for (let i in room) {
		toReturn += room[i].board_id
		toReturn += "`"
		toReturn += room[i].name
		toReturn += "`"
		toReturn += room[i].broadcasting ? 1 : 0
		toReturn += "|"
	}
	console.log(toReturn)
	return toReturn
}

//Broadcast each room
function refresh() {
	//console.log("Refreshing")
	for (let i in rooms) {
		io.in(i).emit('room', formatRoom(rooms[i]))
	}
}

console.log('Listening!')
