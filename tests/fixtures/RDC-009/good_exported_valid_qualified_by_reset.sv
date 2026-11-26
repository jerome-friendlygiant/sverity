// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A data output forced low during reset combines one reset with a functional signal and has no reset word in its name: not an exported reset.
// expect-none: RDC-009
module top(input logic clka, rst, d, output logic valid_o);
  logic valid_q;
  always_ff @(posedge clka or posedge rst) if (rst) valid_q <= 1'b0; else valid_q <= d;
  assign valid_o = rst ? 1'b0 : valid_q;
endmodule
