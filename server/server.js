const io = require('socket.io')(4098)

let rooms = {}

setInterval(refresh, 10000)

io.on('connection', (client) => {
	console.log('Client joining!')
	client.on('register', function(msg) {
		console.log("Adding to room")
		let info = msg.split(",")
		if (!rooms[info[0]]) {
			rooms[info[0]] = {}
		}
		rooms[info[0]][client.id] = {
			board_id: info[1],
			name: info[2],
			broadcasting: false
		}
		client.join(info[0])
		io.in(info[0]).emit('room', formatRoom(rooms[info[0]]))
		console.log(rooms)
	})

	client.on('zap', function(msg) {
		console.log("Zap request received")
		let data = msg.split(',')
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
		for (let i in rooms) {
			if (rooms[i][client.id]) {
				rooms[i][client.id].broadcasting = false
				io.in(i).emit('room', formatRoom(rooms[data[0]]))
			}
		}
		console.log(rooms)
	})

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

function refresh() {
	//console.log("Refreshing")
	for (let i in rooms) {
		io.in(i).emit('room', formatRoom(rooms[i]))
	}
}

console.log('Listening!')
