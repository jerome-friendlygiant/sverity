module top(input logic clk,
  por_n,
  enable,
  output logic [3:0] count);

  (* async_reg = "true" *) logic [1:0] release_pipe;
  wire local_n = release_pipe[1];
  always_ff @(posedge clk or negedge por_n) begin
    if (!por_n) release_pipe <= '0;
    else release_pipe <= {release_pipe[0], 1'b1};
  end
  always_ff @(posedge clk or negedge local_n) begin
    if (!local_n) count <= '0;
    else if (enable) count <= count + 1'b1;
  end

endmodule
