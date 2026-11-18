// sverity-args: --top top
// A `default:` element of a pattern is checked like a list element: an 8-bit value into a parameter-width element truncates.
// revert: input logic [7:0] a => input logic [3:0] a
// revert-expect-none: LNT-057
// expect: LNT-057 WARNING 8
// expect-confidence: LNT-057 heuristic 8
module top #(parameter W=4)(input logic [7:0] a, output logic [W-1:0] q[0:1]);
  assign q='{default:a};
endmodule
