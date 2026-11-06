module top (
  input  logic clk_i,
  input  logic functional_rst_ni,
  input  logic scan_mode_i,
  input  logic scan_rst_ni,
  input  logic data_i,
  output logic data_o
);
  logic selected_rst_ni;
  logic data_q;

  // scan_rst_ni is not synchronized before it becomes a functional reset.
  assign selected_rst_ni = scan_mode_i ? scan_rst_ni : functional_rst_ni;

  always_ff @(posedge clk_i or negedge selected_rst_ni) begin
    if (!selected_rst_ni) data_q <= 1'b0;
    else data_q <= data_i;
  end

  assign data_o = data_q;
endmodule
