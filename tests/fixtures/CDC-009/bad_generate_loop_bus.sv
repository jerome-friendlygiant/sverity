// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// Regression from the independent audit: this hazard must never be classified as safe.
// A 4-bit binary bus synchronised bit-by-bit through a generate loop of a 2-flop cell.
module sync2(input logic clk, d, output logic q);
  logic s1;
  always_ff @(posedge clk) begin s1 <= d; q <= s1; end
endmodule
module top(input logic clka, clkb, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] a_q;
  always_ff @(posedge clka) a_q <= d;   // arbitrary multi-bit data bus
  for (genvar i = 0; i < 4; i++) begin : g_sync
    sync2 u_s (.clk(clkb), .d(a_q[i]), .q(q[i]));
  end
endmodule
