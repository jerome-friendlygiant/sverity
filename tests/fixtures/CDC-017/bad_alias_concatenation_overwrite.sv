// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// expect-none: CDC-018
// False Gray: alias = {0,bin}, then ext[1] := bin[3] through a concatenation LHS; gray 0001->0010 flips two bits.

module top(input logic clka, clkb, rst_n, inc, sel, output logic [3:0] q);
  logic [3:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  logic [4:0] ext; logic dummy;
  always_comb begin ext = {1'b0, bin}; {ext[1], dummy} = {bin[3], 1'b0}; end
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gray <= '0; else gray <= bin ^ ext[4:1];
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, gray};
  assign q = s2;
endmodule
