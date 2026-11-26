// Memory-controller reset built from a boot reset and a run request.
module top (
  input  logic clk_a,
  input  logic por_ni,
  input  logic run_i,
  output logic mc_rst_no
);
  logic [1:0] boot_rst_q;
  logic       run_rst_q;
  logic       mc_rst_q;

  always_ff @(posedge clk_a or negedge por_ni)
    if (!por_ni) boot_rst_q <= 2'b00;
    else         boot_rst_q <= {boot_rst_q[0], 1'b1};

  always_ff @(posedge clk_a or negedge por_ni)
    if (!por_ni) run_rst_q <= 1'b0;
    else         run_rst_q <= run_i;

  always_ff @(posedge clk_a or negedge por_ni)
    if (!por_ni) mc_rst_q <= 1'b0;
    else         mc_rst_q <= run_rst_q & boot_rst_q[1];

  assign mc_rst_no = mc_rst_q;
endmodule
