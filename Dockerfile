FROM gcc:latest
WORKDIR /app
COPY . .
RUN g++ server.cpp -o server
CMD ["./server"]
