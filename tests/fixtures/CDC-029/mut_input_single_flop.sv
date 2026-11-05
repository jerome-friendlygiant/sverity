// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of bad_unsynchronised_input.sv: the input is captured by a single flop, the second stage is missing.
// expect: CDC-029 WARNING
// revert: else q <= d; => else {q, s1} <= {s1, d};
// revert-expect-none: CDC-029 WARNING
module top(input logic clka, rst_n, d, output logic q);
  logic s1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
