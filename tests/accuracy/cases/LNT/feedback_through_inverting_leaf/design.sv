module invert_gate(input logic a,
  enable,
  output logic y);

  assign y = enable ? ~a : 1'b0;

endmodule
module top(input logic enable,
  output wire feedback);

  wire folded;
  invert_gate u_gate(.a(feedback), .enable(enable), .y(folded));
  assign feedback = folded;
  // This connection has no intervening state element.

endmodule
