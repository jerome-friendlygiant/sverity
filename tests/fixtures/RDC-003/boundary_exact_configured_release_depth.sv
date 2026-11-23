// sverity-args: --top top --goal rdc --option RDC-006.min_stages=3
// expect: RDC-003 INFO
// expect-none: RDC-003 WARNING
// expect-none: RDC-006
module top(input logic clk, arst_n, d, output logic q);
  logic [2:0] release_pipe;
  wire local_rst_n = release_pipe[2];

  always_ff @(posedge clk or negedge arst_n)
    if (!arst_n) release_pipe <= '0;
    else release_pipe <= {release_pipe[1:0], 1'b1};

  always_ff @(posedge clk or negedge local_rst_n)
    if (!local_rst_n) q <= 1'b0;
    else q <= d;
endmodule
