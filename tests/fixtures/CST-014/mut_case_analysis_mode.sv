// sverity-args: --top top --sdc @DIR@/bad_hidden_crossing.sdc
// Mutation: set_case_analysis discloses unanalysed alternative under CST-014; removing it clears CST-014.
// expect: CST-014 WARNING
// revert: bad_hidden_crossing.sdc => good_no_mode.sdc
// revert-expect-none: CST-014 WARNING
module top(input logic clka, clkb, mode, d, output logic q);
  logic a_q;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) q <= mode ? a_q : 1'b0;
endmodule
