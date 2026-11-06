module gate_control (
  input logic clk_cfg,
  input logic arst_n,
  input logic enable_request,
  output logic enable_async
);
  always_ff @(posedge clk_cfg or negedge arst_n) begin
    if (!arst_n) enable_async <= 1'b0;
    else enable_async <= enable_request;
  end
endmodule

module gated_worker #(
  parameter int WIDTH = 8
) (
  input logic clk_main,
  input logic arst_n,
  input logic enable_async,
  output logic gated_clk,
  output logic [WIDTH-1:0] work_count
);
  assign gated_clk = clk_main & enable_async;

  always_ff @(posedge gated_clk or negedge arst_n) begin
    if (!arst_n) work_count <= '0;
    else work_count <= work_count + 1'b1;
  end
endmodule

module top (
  input logic clk_main,
  input logic clk_cfg,
  input logic arst_n,
  input logic enable_request,
  output logic gated_clk,
  output logic [7:0] work_count
);
  logic enable_async;
  gate_control u_control (
    .clk_cfg(clk_cfg), .arst_n(arst_n), .enable_request(enable_request),
    .enable_async(enable_async)
  );
  gated_worker #(.WIDTH(8)) u_worker (
    .clk_main(clk_main), .arst_n(arst_n), .enable_async(enable_async),
    .gated_clk(gated_clk), .work_count(work_count)
  );
endmodule
