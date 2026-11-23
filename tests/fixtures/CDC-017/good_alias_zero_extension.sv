// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-018 WARNING
// expect-unresolved: CDC-018
// expect-none: CDC-017
// Control: exact zero-extension Gray, flat.
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin, gray, s1, s2;
  logic [4:0] ext;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  assign ext = {1'b0, bin};
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gray <= '0; else gray <= bin ^ ext[4:1];
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, gray};
  assign q = s2;
endmodule
