// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of good_gray_pointer.sv: the binary counter is crossed instead of its Gray code.
// expect: CDC-017 ERROR
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, bin};
  assign q = s2;
endmodule
