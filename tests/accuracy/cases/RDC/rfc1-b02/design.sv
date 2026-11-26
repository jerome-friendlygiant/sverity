// Selects a boot or run reset for the memory controller.
module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic por_ni,
  input  logic run_i,
  output logic mc_rst_no
);
  logic [1:0] boot_rst_q;
  logic       run_rst_q;
  logic       run_sel_q;

  always_ff @(posedge clk_a or negedge por_ni)
    if (!por_ni) boot_rst_q <= 2'b00;
    else         boot_rst_q <= {boot_rst_q[0], 1'b1};

  always_ff @(posedge clk_a or negedge por_ni)
    if (!por_ni) run_rst_q <= 1'b0;
    else         run_rst_q <= run_i;

  always_ff @(posedge clk_b or negedge por_ni)
    if (!por_ni) run_sel_q <= 1'b0;
    else         run_sel_q <= run_i;

  assign mc_rst_no = run_sel_q ? run_rst_q : boot_rst_q[1];
endmodule
