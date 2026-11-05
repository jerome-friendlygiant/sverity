// sverity-args: --top top --sdc @DIR@/skew.sdc
// expect-none: CDC-018 WARNING
// Near miss of bad_gray_no_constraint.sv: the set_max_delay on the pointer path is present (skew.sdc), so the rule reports INFO instead of WARNING. The counter is 3 bits wide, not 4, so the body does not repeat good_gray_with_constraint.
module top(input logic clka, clkb, rst_n, inc, output logic [2:0] q);
  logic [2:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gray <= '0; else gray <= bin ^ (bin >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, gray};
  assign q = s2;
endmodule
