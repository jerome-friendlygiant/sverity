// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Two consecutive writes under the same `inc` guard where the second overwrites the binary increment
// with a Johnson update must not emit CDC-017 ERROR.
// expect: CDC-017 WARNING
// expect-unresolved: CDC-017
// expect-none: CDC-017 ERROR
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin, s1, s2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) bin <= '0;
    else begin
      if (inc) bin <= bin + 1'b1;
      if (inc) bin <= {bin[2:0], ~bin[3]};
    end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {s2, s1} <= '0;
    else {s2, s1} <= {s1, bin};
  assign q = s2;
endmodule
