// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The registered select is synchronous to clka but asynchronous to the other switched clock clkb.
// expect: CDC-031 WARNING
// expect-confidence: CDC-031 unresolved
module top(input logic clka, clkb, rst_n, d, choose_b, output logic q);
  logic sel, mclk;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) sel <= 1'b0; else sel <= choose_b;
  assign mclk = sel ? clkb : clka;
  always_ff @(posedge mclk or negedge rst_n)
    if (!rst_n) q <= 1'b0; else q <= d;
endmodule
