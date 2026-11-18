// sverity-args: --top top --option LNT-010.empty_output=info
// Under the policy, an output written empty is advisory; an empty input is never demoted (it floats).
// expect: LNT-010 INFO 11
// expect: LNT-010 INFO 12
// expect: LNT-010 WARNING 12
module child(input logic a, input logic b, output logic y, output logic z);
  assign y = a & b;
  assign z = a | b;
endmodule
module top(input logic a, input logic b, output logic y, output logic y2);
  child u_named(.a(a), .b(b), .y(y), .z());
  child u_positional(a, , y2, );
endmodule
