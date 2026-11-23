module case_gf02(input logic wclk, input logic rclk, input logic rst_n, input logic wpush, input logic rpop, output logic [3:0] wgray_seen, output logic [3:0] rgray_seen);
 logic [3:0] wbin,wgray,rbin,rgray,rs1,ws1;
 always_ff @(posedge wclk or negedge rst_n) if(!rst_n) begin wbin<=0;wgray<=0;rs1<=0;rgray_seen<=0;end else begin if(wpush) begin wbin<=wbin+1'b1;wgray<=((wbin+1'b1)>>1)^(wbin+1'b1);end rs1<=rgray;rgray_seen<=rs1;end
 always_ff @(posedge rclk or negedge rst_n) if(!rst_n) begin rbin<=0;rgray<=0;ws1<=0;wgray_seen<=0;end else begin if(rpop) begin rbin<=rbin+1'b1;rgray<=((rbin+1'b1)>>1)^(rbin+1'b1);end ws1<=wgray;wgray_seen<=ws1;end
endmodule
