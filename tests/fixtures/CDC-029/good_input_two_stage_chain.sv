// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of bad_unsynchronised_input.sv: din is captured by a two-stage chain before q, so the input is a recognised synchroniser (INFO, not WARNING).
// expect: CDC-029 INFO
// expect-none: CDC-029 WARNING
module top(input logic clka, rst_n, d, output logic q);
  logic s1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) s1 <= 1'b0; else s1 <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= s1;
endmodule
