// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A 2-stage reset synchroniser built from separate flop instances connected through an internal wire must not trigger RDC-016.
// expect-none: RDC-016
module sync_flop #(parameter logic ResetValue = 1'b0)
  (input logic clk_i, rst_ni, d_i, output logic q_o);
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) q_o <= ResetValue;
    else q_o <= d_i;
  end
endmodule
module sync2(input logic clk_i, rst_ni, d_i, output logic q_o);
  logic d_o, intq;
  always_comb d_o = d_i;
  sync_flop #(.ResetValue(1'b0)) u_sync_1 (.clk_i, .rst_ni, .d_i(d_o), .q_o(intq));
  sync_flop #(.ResetValue(1'b0)) u_sync_2 (.clk_i, .rst_ni, .d_i(intq), .q_o);
endmodule
module top(input logic clka, clkb, rst_ni, req, d, output logic q);
  logic rg, rst_b;
  always_ff @(posedge clka or negedge rst_ni) if (!rst_ni) rg <= 1'b0; else rg <= req;
  sync2 u_sync (.clk_i(clkb), .rst_ni(rg), .d_i(1'b1), .q_o(rst_b));
  always_ff @(posedge clkb or negedge rst_b) if (!rst_b) q <= 1'b0; else q <= d;
endmodule
