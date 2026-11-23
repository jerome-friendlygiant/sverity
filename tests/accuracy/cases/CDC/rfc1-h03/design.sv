// Status flag transmitter and receiver in separate modules.
module status_tx (
  input  logic clk,
  input  logic set_i,
  output logic status_q
);
  always_ff @(posedge clk) status_q <= set_i;
endmodule

module status_rx (
  input  logic clk,
  input  logic status_i,
  output logic cap_q
);
  always_ff @(posedge clk) cap_q <= status_i;
endmodule

module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic set_i,
  output logic ready_o
);
  logic status;

  status_tx u_tx (.clk(clk_a), .set_i(set_i), .status_q(status));
  status_rx u_rx (.clk(clk_b), .status_i(status), .cap_q(ready_o));
endmodule
