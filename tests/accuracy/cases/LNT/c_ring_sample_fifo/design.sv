// Sample FIFO, 8 entries. The read register follows the entry at the read
// pointer every cycle; empty tells the consumer whether it holds a sample.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       push,
  input  logic [9:0] din,
  input  logic       pop,
  output logic [9:0] rdata,
  output logic       empty,
  output logic       full
);
  logic [9:0] mem [0:7];
  logic [2:0] wptr;
  logic [2:0] rptr;
  logic [3:0] count;
  logic       do_push;
  logic       do_pop;

  assign do_push = push && !full;
  assign do_pop  = pop && !empty;
  assign empty   = (count == 4'd0);
  assign full    = (count == 4'd8);

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      wptr  <= 3'd0;
      rptr  <= 3'd0;
      count <= 4'd0;
    end else begin
      if (do_push) wptr <= wptr + 3'd1;
      if (do_pop)  rptr <= rptr + 3'd1;
      count <= count + {3'd0, do_push} - {3'd0, do_pop};
    end
  end

  always_ff @(posedge clk) begin
    if (do_push) mem[wptr] <= din;
  end

  always_ff @(posedge clk) begin
    rdata <= mem[rptr];
  end
endmodule
