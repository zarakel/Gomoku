// 1. Create Pixi application
const app = new PIXI.Application({
    width: window.innerWidth,
    height: window.innerHeight,
    backgroundColor: 0xE5B066,
    antialias: true,
    autoDensity: true,
    resolution: window.devicePixelRatio || 1
});

document.body.appendChild(app.view);

// --- WEBSOCKET CONNECTION ---
const ws = new WebSocket('ws://localhost:8000');
let gameState = {
    board: new Array(361).fill(0),
    turn: 1,
    captures_p1: 0,
    captures_p2: 0,
    game_over: false,
    hint_idx: -1
};

ws.onopen = () => {
    console.log('✅ Connected to WebSocket server');
    updateInfoDisplay('Connected to server ✅');
};

ws.onmessage = (event) => {
    try {
        const data = JSON.parse(event.data);
        gameState = data;
        console.log('📨 Board state updated:', data);
        updateBoardFromServer(data);
    } catch (e) {
        console.error('Error parsing WebSocket message:', e);
    }
};

ws.onerror = (error) => {
    console.error('❌ WebSocket error:', error);
    updateInfoDisplay('Server connection error ❌');
};

ws.onclose = () => {
    console.log('❌ Disconnected from WebSocket server');
    updateInfoDisplay('Disconnected from server ❌');
};

// Function to send move to backend
function sendMove(index) {
    const message = {
        action: 'play',
        index: index
    };
    ws.send(JSON.stringify(message));
}

// Function to send reset command
function sendReset() {
    const message = { action: 'reset' };
    ws.send(JSON.stringify(message));
}

// Function to request hint
function sendHint() {
    const message = { action: 'hint' };
    ws.send(JSON.stringify(message));
}

// Function to toggle AI
function sendToggleIA() {
    const message = { action: 'toggle_ia' };
    ws.send(JSON.stringify(message));
}

// --- INFO DISPLAY ---
const infoContainer = new PIXI.Container();
app.stage.addChild(infoContainer);

const infoBg = new PIXI.Graphics();
infoBg.beginFill(0x000000, 0.7);
infoBg.drawRect(10, 10, 300, 150);
infoBg.endFill();
infoContainer.addChild(infoBg);

const infoText = new PIXI.Text('Initializing...', {
    fontFamily: 'Arial',
    fontSize: 16,
    fill: 0xFFFFFF
});
infoText.x = 20;
infoText.y = 20;
infoContainer.addChild(infoText);

function updateInfoDisplay(text) {
    infoText.text = text;
}

function refreshInfoDisplay() {
    const playerName = gameState.turn === 1 ? 'Black' : 'White';
    const info = `
Turn: ${playerName}
Captures P1: ${gameState.captures_p1}
Captures P2: ${gameState.captures_p2}
Game Over: ${gameState.game_over ? 'YES' : 'NO'}
Status: Connected ✅
    `.trim();
    infoText.text = info;
}

// --- BOARD SIZE & LAYOUT ---
const BOARD_SIZE = 19;
const CELL_SIZE = 40;
const PADDING = 40;

const boardContainer = new PIXI.Container();
app.stage.addChild(boardContainer);

boardContainer.x = (app.screen.width - (BOARD_SIZE * CELL_SIZE)) / 2;
boardContainer.y = (app.screen.height - (BOARD_SIZE * CELL_SIZE)) / 2;

// --- DRAW GRID ---
const grid = new PIXI.Graphics();
grid.lineStyle(2, 0x000000, 0.5);

for (let i = 0; i < BOARD_SIZE; i++) {
    grid.moveTo(i * CELL_SIZE, 0);
    grid.lineTo(i * CELL_SIZE, (BOARD_SIZE - 1) * CELL_SIZE);
    
    grid.moveTo(0, i * CELL_SIZE);
    grid.lineTo((BOARD_SIZE - 1) * CELL_SIZE, i * CELL_SIZE);
}
boardContainer.addChild(grid);

// Interactive grid
grid.eventMode = 'static';
grid.hitArea = new PIXI.Rectangle(0, 0, BOARD_SIZE * CELL_SIZE, BOARD_SIZE * CELL_SIZE);

// Store placed stones
const stones = {};

grid.on('pointerdown', (event) => {
    const localPos = grid.toLocal(event.global);
    const gridX = Math.round(localPos.x / CELL_SIZE);
    const gridY = Math.round(localPos.y / CELL_SIZE);

    // Clamp to board bounds
    if (gridX >= 0 && gridX < BOARD_SIZE && gridY >= 0 && gridY < BOARD_SIZE) {
        const index = gridY * BOARD_SIZE + gridX;
        
        // Only allow placement on empty cells and not during AI turn
        if (gameState.board[index] === 0) {
            console.log(`Sending move to server: ${gridX}, ${gridY} (index: ${index})`);
            sendMove(index);
        } else {
            console.log('Cell already occupied');
        }
    }
});

function placeStone(x, y, player) {
    const stoneKey = `${x},${y}`;
    
    // Don't place twice
    if (stones[stoneKey]) return;
    
    const stone = new PIXI.Graphics();
    const color = (player === 1) ? 0x111111 : 0xEEEEEE;
    
    stone.beginFill(color);
    stone.lineStyle(1, 0x000000, 0.2);
    stone.drawCircle(0, 0, CELL_SIZE * 0.45);
    stone.endFill();

    stone.x = x * CELL_SIZE;
    stone.y = y * CELL_SIZE;
    stone.scale.set(0);
    
    boardContainer.addChild(stone);
    stones[stoneKey] = stone;

    // Animation
    let scale = 0;
    const animate = () => {
        scale += 0.1;
        if (scale >= 1) {
            stone.scale.set(1);
            app.ticker.remove(animate);
        } else {
            stone.scale.set(scale);
        }
    };
    app.ticker.add(animate);
}

// Draw hint indicator
function drawHint(index) {
    const x = index % BOARD_SIZE;
    const y = Math.floor(index / BOARD_SIZE);
    
    const hint = new PIXI.Graphics();
    hint.lineStyle(2, 0xFFFF00, 0.8);
    hint.drawCircle(0, 0, CELL_SIZE * 0.3);
    hint.endFill();
    
    hint.x = x * CELL_SIZE;
    hint.y = y * CELL_SIZE;
    
    boardContainer.addChild(hint);
    
    // Remove after 2 seconds
    setTimeout(() => {
        boardContainer.removeChild(hint);
    }, 2000);
}

// Update board from server state
function updateBoardFromServer(data) {
    // Place all stones from board array
    for (let i = 0; i < data.board.length; i++) {
        if (data.board[i] !== 0) {
            const x = i % BOARD_SIZE;
            const y = Math.floor(i / BOARD_SIZE);
            placeStone(x, y, data.board[i]);
        }
    }
    
    // Draw hint if exists
    if (data.hint_idx !== -1) {
        drawHint(data.hint_idx);
    }
    
    // Update info display
    refreshInfoDisplay();
}

// Window resize handling
window.addEventListener('resize', () => {
    app.renderer.resize(window.innerWidth, window.innerHeight);
    boardContainer.x = (app.screen.width - (BOARD_SIZE * CELL_SIZE)) / 2;
    boardContainer.y = (app.screen.height - (BOARD_SIZE * CELL_SIZE)) / 2;
});

// --- KEYBOARD CONTROLS ---
document.addEventListener('keydown', (e) => {
    if (e.key === 'r' || e.key === 'R') {
        console.log('Resetting game...');
        sendReset();
        // Clear all stones
        Object.keys(stones).forEach(key => {
            boardContainer.removeChild(stones[key]);
        });
        Object.keys(stones).length = 0;
    }
    if (e.key === 'h' || e.key === 'H') {
        console.log('Requesting hint...');
        sendHint();
    }
    if (e.key === 'i' || e.key === 'I') {
        console.log('Toggling AI...');
        sendToggleIA();
    }
});

console.log('Game initialized. Waiting for server connection...');
console.log('Controls: R=Reset, H=Hint, I=Toggle AI');