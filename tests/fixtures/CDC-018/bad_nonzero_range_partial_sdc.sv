// sverity-args: --top top --sdc @DIR@/nonzero_range_partial.sdc
// Nonzero-based [4:1] vector is not fully covered by SDC bit selectors [0]..[3] (bit [4] is missing).
// expect: CDC-018 WARNING
// expect-unresolved: CDC-018
// expect-none: CDC-018 INFO
module top(input logic clka, clkb, rst_n, inc, output logic [4:1] q);
  logic [4:1] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gray <= '0; else gray <= bin ^ (bin >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, gray};
  assign q = s2;
endmodule
