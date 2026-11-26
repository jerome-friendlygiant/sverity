// Two source ids name one RTL reset input: each is a conflict, and the clause on one of them is rejected.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/same_node.json
// expect: CST-016 WARNING
// expect-count: CST-016 WARNING 3
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_a_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else q <= d;
endmodule
