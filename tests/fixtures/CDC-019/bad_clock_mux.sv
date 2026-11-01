// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: assign mclk = sel ? clka : clkb; => assign mclk = clka;
// revert-expect-none: CDC-019 WARNING
// expect: CDC-019 WARNING
// expect: CDC-031 WARNING
module top(input logic clka, clkb, sel, rst_n, d, output logic q);
  logic mclk;
  assign mclk = sel ? clka : clkb;
  always_ff @(posedge mclk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
