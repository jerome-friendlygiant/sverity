// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// expect-none: CDC-018
// expect-none: CDC-017
// Guarded latching alias of a Gray expression must not be classified as Gray.
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin, gray, s1, s2, alias_g;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1'b1;
  always_comb begin if (inc) alias_g = bin ^ (bin >> 1); end
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gray <= '0; else gray <= alias_g;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, gray};
  assign q = s2;
endmodule
