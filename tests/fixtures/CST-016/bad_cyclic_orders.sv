// Two release orders that contradict each other: each participating order is a decided conflict.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/cyclic_orders.json
// expect: CST-016 WARNING
// expect-count: CST-016 WARNING 2
// expect-confidence: CST-016 established
// expect-coverage: reset-contracts COMPLETE
module top(input logic clka, clkb, rst_a_n, rst_b_n, d, output logic q, r);
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) r <= 1'b0; else r <= d;
endmodule
