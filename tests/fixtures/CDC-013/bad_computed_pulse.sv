// sverity-args: --top top --sdc @DIR@/slow.sdc
// revert: ev <= x & ~y; => ev <= x;
// revert-expect-none: CDC-013 WARNING
// expect: CDC-013 WARNING
module top(input logic clka, clkb, rst_n, x, y, output logic q);
  logic ev, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) ev <= 1'b0; else ev <= x & ~y;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, ev};
  assign q = s2;
endmodule
