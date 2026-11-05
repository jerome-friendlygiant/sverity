// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CDC-019
// Near miss of bad_clock_mux.sv: the clock multiplexer is removed, so the domain root is the single clock clka; sel and clkb are left unused.
module top(input logic clka, clkb, sel, rst_n, d, output logic q);
  logic mclk;
  assign mclk = clka;
  always_ff @(posedge mclk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
