// sverity-args: --top top --sdc @DIR@/skew.sdc
// MUTATION of good_gray_with_constraint.sv: the Gray source register no longer matches the set_max_delay pattern gray*.
// expect: CDC-018 WARNING
// revert: ALL gx => gray
// revert-expect-none: CDC-018 WARNING
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin, gx, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gx <= '0; else gx <= bin ^ (bin >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, gx};
  assign q = s2;
endmodule
