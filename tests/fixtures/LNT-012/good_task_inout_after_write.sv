// sverity-args: --top top
// `cnt` is written before the task reads and updates it, on every path: the call reads the procedure's own value, not feedback.
// expect-none: LNT-012
module top(input logic [3:0] base, output logic [3:0] cnt);
  task automatic inc(inout logic [3:0] v); v = v + 1; endtask
  always_comb begin cnt = base; inc(cnt); end
endmodule
