module top(input logic src_clk,
  dst_clk,
  send,
  
  input logic [7:0] din,
  output logic [7:0] dout);

  logic req = 0, ack = 0;
  logic ack_meta = 0, ack_src = 0, req_meta = 0, req_dst = 0;
  logic [7:0] held = 0;
  always_ff @(posedge src_clk) begin
    ack_meta <= ack;
    ack_src <= ack_meta;
    if (send && (req == ack_src)) begin
      held <= din;
      req <= ~req;
    end
  end
  always_ff @(posedge dst_clk) begin
    req_meta <= req;
    req_dst <= req_meta;
    if (req_dst != ack) begin
      dout <= held;
      ack <= req_dst;
    end
  end

endmodule
