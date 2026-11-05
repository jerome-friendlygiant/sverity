// sverity-args: --top top --sdc @DIR@/three_clocks.sdc
// Two chains into two different domains cannot reconverge without a further crossing: not this rule.
// expect-none: CDC-047
module top(input logic clka, clkb, clkc, rst_n, d, output logic x, y);
  logic launch, a1, a2, c1, c2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) launch <= 0; else launch <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {a2, a1} <= '0; else {a2, a1} <= {a1, launch};
  always_ff @(posedge clkc or negedge rst_n) if (!rst_n) {c2, c1} <= '0; else {c2, c1} <= {c1, launch};
  assign x = a2;
  assign y = c2;
endmodule
