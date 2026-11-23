// C12
module c12(input logic clk, arst_n, output logic rst_n_local);
  (* ASYNC_REG = "TRUE" *) logic r1,r2;
  always_ff @(posedge clk or negedge arst_n) if (!arst_n) begin r1<=0; r2<=0; end else begin r1<=1; r2<=r1; end
  assign rst_n_local=r2;
endmodule
