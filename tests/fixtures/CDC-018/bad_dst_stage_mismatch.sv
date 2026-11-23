// sverity-args: --top top --sdc @DIR@/skew.sdc
// A malformed destination synchronizer chain (stage mismatch and first-stage tap) cannot earn CDC-018 INFO.
// expect: CDC-018 WARNING
// expect-unresolved: CDC-018
// expect-none: CDC-018 INFO
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q, output logic [3:0] tap);
  logic [3:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gray <= '0; else gray <= bin ^ (bin >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s1 <= '0; else s1 <= gray;
  always_ff @(negedge clkb) s2 <= s1;
  assign q = s2;
  assign tap = s1 ^ 4'h5;
endmodule
