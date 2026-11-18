// sverity-args: --top top
// The loop variable is module-level, and a subroutine called from the body writes it to -1. Nothing in the loop's own
// arguments shows that write, so the signed compare with the unsigned bound is not value-preserved: it stays WARNING.
// expect: LNT-005 WARNING 13
module top(input logic [7:0] d, output logic [3:0] n);
  localparam int unsigned LIMIT = 8;
  int i;
  function automatic void drop_i();
    i = -1;
  endfunction
  always_comb begin
    n = '0;
    for (i = 0; i < LIMIT; i++) begin
      drop_i();
      n += d[0];
    end
  end
endmodule
