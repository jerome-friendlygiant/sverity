// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// One launch bit, two independent chains into clkb: the copies can disagree for a cycle.
// revert: {b2, b1} <= {b1, launch}; => {b2, b1} <= {b1, a2};
// revert-expect-none: CDC-047
// expect: CDC-047 WARNING
module top(input logic clka, clkb, rst_n, d, output logic x, y);
  logic launch, a1, a2, b1, b2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) launch <= 0; else launch <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {a2, a1} <= '0; else {a2, a1} <= {a1, launch};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {b2, b1} <= '0; else {b2, b1} <= {b1, launch};
  assign x = a2;
  assign y = b2;
endmodule
