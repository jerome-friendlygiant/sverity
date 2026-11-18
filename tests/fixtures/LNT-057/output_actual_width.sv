// sverity-args: --top top
// expect-count: LNT-057 WARNING 1
// expect-confidence: LNT-057 heuristic
module child(input logic [7:0] d, output logic [7:0] o);
  assign o = d;
endmodule
module top #(parameter Q=3)(input logic [7:0] d, output logic [2:0] fixed_q, output logic [Q-1:0] parameter_q);
  child fixed_connection(.d(d), .o(fixed_q));
  child parameter_connection(.d(d), .o(parameter_q));
endmodule
