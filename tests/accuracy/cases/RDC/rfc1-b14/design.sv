// Passes the core reset through a vendor reset synchroniser and advances a small state machine.
// Library cell: body not supplied.
(* black_box *) module rst_sync_ip (input logic clk, input logic d_ni, output logic q_no);
endmodule

module top (
  input  logic       clk,
  input  logic       rst_ni,
  input  logic       go_i,
  output logic [1:0] state_o
);
  logic       core_rst_n;
  logic [1:0] state_q;

  rst_sync_ip u_rs (.clk(clk), .d_ni(rst_ni), .q_no(core_rst_n));

  always_ff @(posedge clk or negedge core_rst_n)
    if (!core_rst_n) state_q <= 2'd0;
    else if (go_i)   state_q <= state_q + 2'd1;

  assign state_o = state_q;
endmodule
