// sverity-args: --top top
// Constants only: an enumerator plus an int in an enum's initializer, and a generate condition comparing two constants of different signedness.
// expect: LNT-005 INFO 7
// expect: LNT-005 INFO 10
// expect-none: LNT-005 WARNING
package pk;
  typedef enum int unsigned { First = 3, Next = First + 1 } idx_e;
endpackage
module top #(parameter int unsigned N = 4, parameter int unsigned W = 3)(input logic a, output logic y);
  if ($clog2(N + 1) <= W) begin : gen_wide
    assign y = a;
  end else begin : gen_narrow
    assign y = 1'b0;
  end
endmodule
