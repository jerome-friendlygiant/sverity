// sverity-args: --top top
// expect: LNT-057 WARNING
// expect-unresolved: LNT-057
module top #(parameter W=4)(input logic [7:0] d);
  typedef struct packed { logic [W-1:0] field; } word_t;
  word_t words[2];
  assign words = '{'{field:d}, '{field:d}};
endmodule
