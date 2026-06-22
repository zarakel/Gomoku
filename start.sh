# Start C backend in background
./gomoku &
GOMOKU_PID=$!

# Wait for backend to initialize
sleep 1

# Start frontend HTTP server
python3 -m http.server 8080 &
HTTP_PID=$!

echo "✅ Servers started!"
echo "   - Backend: ws://localhost:8000"
echo "   - Frontend: http://localhost:8080"
echo ""
echo "Press Ctrl+C to stop all servers"

# Wait for Ctrl+C
trap "kill $GOMOKU_PID $HTTP_PID 2>/dev/null; exit" INT
wait