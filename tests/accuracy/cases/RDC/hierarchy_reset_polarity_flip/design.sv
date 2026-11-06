module top (
  input  logic clk_i,
  input  logic reset_ni,
  input  logic request_i,
  output logic acknowledge_o
);
  logic child_reset_ni;
  logic       child_ack;

  // The parent boundary changes an active-low reset into an active-high level.
  assign child_reset_ni = ~reset_ni;

  child_block u_child (
    .clk_i(clk_i),
    .reset_ni(child_reset_ni),
    .request_i(request_i),
    .acknowledge_o(child_ack)
  );

  assign acknowledge_o = child_ack;
endmodule

module child_block (
  input logic clk_i,
  input logic reset_ni,
  input logic request_i,
  output logic acknowledge_o
);
  logic ack_q;
  always_ff @(posedge clk_i or negedge reset_ni) begin
    if (!reset_ni) ack_q <= 1'b0;
    else if (request_i) ack_q <= 1'b1;
    else ack_q <= 1'b0;
  end
  assign acknowledge_o = ack_q;
endmodule
