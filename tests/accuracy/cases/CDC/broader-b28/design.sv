module case_gf01(input logic wclk, input logic rclk, input logic rst_n, input logic wen, output logic [3:0] wgray_sync);
 logic [3:0] wbin,wgray; logic [3:0] g1; always_ff @(posedge wclk or negedge rst_n) if(!rst_n) begin wbin<=0;wgray<=0;end else if(wen) begin wbin<=wbin+1'b1;wgray<=((wbin+1'b1)>>1)^(wbin+1'b1);end
 always_ff @(posedge rclk or negedge rst_n) if(!rst_n) begin g1<=0;wgray_sync<=0;end else begin g1<=wgray;wgray_sync<=g1;end
endmodule
