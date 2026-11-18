// sverity-args: --top top
// `go` is data by its name, but a net with a reset-like name (`rst_q`) feeds it, so its identity is undecided. The block it tests loads data
// (`r <= d`) and stores no constant, so it is an enable, not a reset question: nothing is Unresolved.
// expect-none: LNT-068 WARNING
// expect-none: LNT-020 WARNING
module top(input logic clk, rst_n, rst_q, a, input logic [3:0] d, output logic [3:0] q, r);
  logic go;
  assign go = a & ~rst_q;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) q <= 4'd0;
    else q <= d;
  always_ff @(posedge clk) if (go) r <= d;
endmodule
