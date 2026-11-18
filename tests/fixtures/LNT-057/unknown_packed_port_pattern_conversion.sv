// sverity-args: --top top
// expect: LNT-057 WARNING
// expect-unresolved: LNT-057
module child #(parameter W=4)(input struct packed { logic [W-1:0] field; } value);
endmodule
module top(input logic [7:0] d);
  child u(.value('{field:d}));
endmodule
