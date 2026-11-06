module top(input logic clk,
  por,
  output logic enabled);

  (* async_reg = "true" *) logic [1:0] release_pipe;
  always_ff @(posedge clk or posedge por) begin
    if (por) release_pipe <= 2'b11;
    else release_pipe <= {release_pipe[0], 1'b0};
  end
  always_ff @(posedge clk or posedge release_pipe[1]) begin
    if (release_pipe[1]) enabled <= 0;
    else enabled <= 1;
  end

endmodule
