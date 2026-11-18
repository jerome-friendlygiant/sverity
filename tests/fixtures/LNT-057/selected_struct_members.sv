// sverity-args: --top top
// expect-count: LNT-057 WARNING 1
module top #(parameter W=8)(output logic [3:0] a,b);
  struct packed { logic [W-1:0] dependent_field; logic [7:0] fixed_field; } x;
  assign a=x.fixed_field;
  assign b=x.dependent_field;
endmodule
